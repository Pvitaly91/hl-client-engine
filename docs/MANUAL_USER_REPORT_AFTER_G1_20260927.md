# Manual user confirmation after G1, recorded 2026-09-27

This is a new record of the user's latest confirmation, not a new G1 task,
build or agent-started live session. Prior G1 reports with manual validation
not_run remain historical records and are not rewritten.

## User-reported functionality

attribution=user_report; manual functionality=passed:

- WASD, Shift, jump, duck, crouch-walk and smooth prediction;
- Glock/crowbar selection, firing, reload and animations;
- HUD and camera-local viewmodel.

This does not independently confirm recoil, combat damage/armor absorption,
D1 pickups or live item respawn. It does not turn a nonzero application exit
into a successful managed run. The user identifies the same historical run
with server PID 30708 and client PID 33896, not a newly captured transaction.

## Exact run and termination boundary

Retained transaction directory:
manual-artifacts/research-copy-smoke/d2382267b69343eb9e96461a9d2ba169.
All seven files have publication time 2026-09-27 03:14:32 local. The staged
native report binds both PIDs, valve/boot_camp, port 27243, direct_loopback,
renderer=opengl, live-input=keyboard-mouse, prediction=reference, accepted
connection, schema registry, runtime publication and entered-game evidence.
The complete available bounded client/server/guard excerpts and metadata were
read again, not just a summary selected by PID or directory recency.

- Application exit=2, primary_error=runtime_record_failed,
  close=not_requested, records_attempted=361, records_committed=360.
- Prediction active, coverage limited; scripted motion not_evaluated. Neither
  coverage verdict proves the primary cause. Scheduler underlying_error=none;
  gl_errors=0.
- Native managed primary_failure=project-client-nonzero-exit,
  client-ready=false; last stage=live_service_payloads_received.
- Native owned_process_cleanup=exact; client application cleanup=complete.
- Restoration unknown: restoration_status=wrapper_pending in staged JSON;
  no final transaction-bound wrapper report is retained.

Original stdout/parser context and the rejected wire body are unavailable;
only the bounded redacted excerpt remains. Metadata reports capture_failed
false and no capture truncation, which is not proof that the later publication
retained every original line. Exact decoder enum/opcode/cursor/semantic field
cannot be recovered offline. No speculative crash fix or exit-code masking is
claimed. D1 already separates healthy manual application outcome from coverage
and retains typed first-error diagnostics; genuine errors remain nonzero.

## Current D1 handoff, not a repeated implementation

Existing result: halflife_pickups_inventory_integrated_offline_verified.
The production GameClientHost/API/Half-Life module already implements owning
AmmoPickup/WeapPickup/ItemPickup feedback, independent absolute AmmoX/Health/
Battery/CurWeapon/clientdata state, provenance and atomic commit. Inventory
uses server ownership. Four bounded feedback rows expire after five seconds
on presentation clock without replay duplication. Generic server snapshots,
effects and asset reuse determine item visibility/return, not local proximity,
PVS or a local respawn timer. No new missing implementation was found in this
scope, so production code and completed build outputs were not changed.

Rechecked current Debug and Release D1/manual-result tests: each 10 passed,
318 assertions. Existing HL1-disabled core-only/alternate-module executable:
93 passed, 1,318 assertions, no Half-Life fallback. Seed 2532982175.
The prior affected Debug/Release/ASan builds and focused B1/C/H3/H4/A1 gates
remain recorded in the D1 contract, with 212 passed + 1 opt-in skip per focused
configuration. Prior actual GL gates passed. The previous single broad suite
(2,097 passed + 21 skips) was not repeated, relabeled or treated as a new build.
Only new follow-up logs are added; no old test report is overwritten.

Launcher reference/off and Scenario damage-respawn-check are unchanged.
CheckOnly rechecks and git diff --check are recorded separately under
manual-artifacts/task-records/d1-followup-*. The Release/helper hashes and
seven historical hashes match the completed D1 handoff/prechange inventory.
Release hlclient.exe SHA-256:
20610194D4EEC2C6897BD0FE592D03329CBB3D6ACBD603330BE180BA683E718E.

See [D1 implementation, gates and checklist](HALFLIFE_PICKUPS_INVENTORY_M473D1.md).

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

For the next user test, walk to available items normally: one pickup row and
separate server ammo/HP/ARM/ownership update, no double addition or optimistic
gain. Test healthkit only at genuinely non-full HP. Item return and repeat
pickup depend on the server. New D1 live/manual verification remains not_run.
No new live sessions, cleanup, G1 rebuild, staging, commit or push performed.
