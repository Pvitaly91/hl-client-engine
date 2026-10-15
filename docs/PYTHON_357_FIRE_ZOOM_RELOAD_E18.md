# HLC-E18 — Python/.357 fire, zoom and reload

Worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`; branch
`codex/stock-runtime-campaign-5e48b7c1`; inspected HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. No checkout of the historical
remote checkpoint. Pre-edit snapshot: 1,218 ordinary project files in
`manual-artifacts/task-records/e18-python-357-20261004/source`, hash-verified.
Existing E11–E17 and unrelated dirty files remain preserved.

## Additive manual evidence

New E17 confirmation, `attribution=user_report`: single/double fire,
one/two shotshells, pellets, shell-by-shell reload and interruption, remote
Shotgun, corrected near-camera band, and retained visible reload shell work.
This does not overwrite historical E17 defect reports or historical
`manual=not_run`. E18/new executable: `manual=not_run`.
Additive E17 acceptance: `manual=passed`, `attribution=user_report` for the
listed checks, not an invented stress-test or stock-parity confirmation.

## Pre-edit boundary audit

| Boundary | Before E18 | Integration |
|---|---|---|
| Catalogue/inventory | Generic committed WeaponList, owned bits, server clips/reserves already work | .357 aliases + ID 3 + validated model roles in Half-Life only |
| v/p/w/ammo model loading | Generic manifest slot/importer/Studio support already exists | Reuse unchanged, never fixed numeric model slots |
| Python event | Framing exists, no semantic binding | `events/python.sc` assigned index, local echo/remote owned events |
| Local fire/reload | No .357 roles/eligibility | Existing controller, sequence 2/3, 0.75 s/2 s, server ammo |
| SetFOV | Strict one-byte Half-Life decoder already exists; unused projection | Retained owning provenance, atomic publication, unsigned value |
| clientdata FOV | Generic delta field retained, no typed projection | Optional exact unsigned DT_FLOAT projection; no ABI defaults |
| Runtime FOV | Fixed vertical 60 degrees | Neutral horizontal-degree intent converted using actual viewport aspect |
| Zoom | Not integrated | Server-confirmed 40/0; edge-triggered RMB before command history |

## Reference and actual approved resources

Read-only official SDK revision:
`b1b5cf5892918535619b2937bb927e46cb097ba1`, `dlls/python.cpp`,
`dlls/weapons.h`, `dlls/weapons.cpp`, `cl_dll/ev_hldm.cpp`,
`cl_dll/hl/hl_events.cpp`, `cl_dll/hud.cpp`, `dlls/player.cpp`,
`network/delta.lst`. No implementation bodies copied. `WEAPON_PYTHON=3`,
aliases `weapon_357`/`weapon_python`, 357 ammunition, cylinder capacity 6,
one 8192-unit bullet, server 1-degree cone, 0.75 s primary cycle; event
`python.sc`, two shot samples at 0.8–0.9 gain, normal pitch, -10 pitch punch,
bright flash and no brass/tracer. Multiplayer secondary toggles 40/default,
0.5 s cooldown. Conventional reload uses sequence 3, 2 s and clears zoom.
Server alone owns actual bullet RNG/trace/damage, inventory and score.

Projection reference: pinned `cl_dll/view.cpp:1737` CalcFov accepts `fov_x`
and width/height; its vertical result is algebraically
`2*atan(tan(horizontal/2)/(width/height))`. Its screen-ray use at line 1769
keeps horizontal `in_fov` separately. The engine uses that neutral conversion,
not a copied implementation body or a directly assigned vertical 40.

| Actual model | SHA-256 | Inspected metadata |
|---|---|---|
| v_357 | `12791F64091AA572AE35AAD40EE74B5BB3EE30F59426E6FFA045D9AD1D731175` | 66 bones; 8 sequences; one skin family; five bodyparts; one muzzle attachment on bone 11 at (0,-14,6) |
| p_357 | `DD7DC70A352ECE3A8022B68F216A1E0F16D42708F02FBBABFA61B45C80675815` | 12 named bones; one one-frame idle; one skin/body; no attachment; 83 vertices on Cylinder01 bone 11 |
| w_357 | `9188F3D6B22EE0A72B951C523F3F6684E566F61D8F376B5B3A746EAE60248F4F` | Existing generic world resource |
| w_357ammobox | `6E6D8FA5E868D9BDF740D6AA39D58C495661ECEDABC519522E60D2ABCC58DE63` | Existing generic pickup resource |

v-model roles 0..7: idle1, fidget1, fire1, reload, holster, draw, idle2,
idle3; frames 71/171/31/111/16/16/61/89, fps 30 except reload 36. Fire marker
5001 at frame 0, option 31. Reload marker 5004 at frame 70,
`weapons/357_reload1.wav`. Bodyparts gun/bullets/speedload/hands each have
one model; scope has blank/body 0 and python_lasersight/body 1. MP viewmodel
body 1 is independent of zoom, and p-model remains body 0.

Pinned Python passes its MP body argument into DefaultDeploy and DefaultReload.
Deploy forwards it to SendWeaponAnim. The client-weapons DefaultReload in
`cl_dll/hl/hl_weapons.cpp` forwards body; the server implementation in
`dlls/weapons.cpp` does not forward that parameter in its reload call. Thus MP
local/provisional reload uses the validated body 1, but any exact committed
svc_weaponanim body remains authoritative; no global p/world body override is
invented. This distinction is not hidden behind a stock-binary parity claim.

p-model Cylinder01 translation (5.854939,.808541,2.847965), rotation
(-1.564202,-3.113941,-1.474388). Source bounds (-1.06,-4.49,-3.67)..
(1.05,1.56,8.06). The inspected barrel-end vertices are at Z=8.06,
center X=0,Y=.03. Validated local-compatible muzzle socket is (0,.03,8.10)
on that bone, +Z forward, +X right, -Y up. No MP5 socket reuse. Generic
named-bone merge still follows the server player palette/sequence; fire events
never force PLAYER_ATTACK1.

## Ownership, projection and lifecycle

Half-Life catalogue/roles, eligibility, sound names, body choice, event
semantics and FOV reconciliation remain in `games/halflife`. Core exposes
optional clientdata FOV, provenance, a neutral projection intent and a bounded
pre-history edge-button mask. Renderer has no Python/SetFOV knowledge.
Dependency direction stays composition -> host/API + concrete HL module;
lower engine libraries do not link HL1.

FOV uses latest committed source ordinal/cursor, not repeated retained values.
0 is normal horizontal 90 degrees; valid nonzero 1..179 is server FOV;
invalid values degrade to 90 with typed status. Vertical radians are
`2*atan(tan(horizontal/2)/actual_aspect)`. This explicitly corrects the old
fixed-vertical normal view to the HL horizontal convention; position, yaw,
pitch, quantized input and physics are unchanged. No provisional zoom or stock
sensitivity parity is claimed by the original E18 implementation. Sensitivity
was initially unchanged; the 2026-10-15 corrective below supersedes that limit.
No new crosshair sprite subsystem is introduced here.

Reload, holster, death/new life and map/session reset clear projection; a
source floor prevents retained old 40 from returning. Server SetFOV/clientdata
remain final authority. Parser grammar and historical canonical-hash version
are unchanged; added projection/provenance fields do not redefine old hashes.
RMB hold suppression occurs before history insertion through the existing
validated adapter; the committed bits/history/replay are never rewritten.
Pending press is consumed only after accepted history insertion. Other weapons
retain held secondary behavior.

## Actual production paths

Concrete behavior remains in target `hlclient_game_halflife`:
`firearm_profile.hpp`, `local_weapon_presentation.cpp`, `weapon_audio.cpp`,
`weapon_visuals.cpp`, `remote_effects.cpp`, `server_messages.cpp`,
`presentation.cpp`, and `client_module.cpp` under the existing HL directories.
Assigned events, server clips/reserves, exact model metadata, action/restart
identities and the established E13/E14 policy are reused, not parallel systems.
Reload sound is the validated v357 sequence-3/frame-70 event 5004 delivered
once by WeaponAudio; Python Reload has no extra explicit SDK reload EmitSound.
The independently owned server svc_sound path is not replayed by this module.

Neutral changes are optional observation/provenance in `client/runtime_observation`,
unsigned delta FOV projection in `goldsrc/runtime_replay_session`, projection
and edge-input types in `game_api`, bounded host forwarding in
`game_api/game_client_host`, pre-history press delivery in the existing
`goldsrc/live_runtime_stage`/`usercmd_input_adapter`, and conversion in
`app/live_visual_control`. Normal `apps/hlclient/main.cpp` supplies the actual
pixel aspect. Composition still links host/API + HL module; lower targets
never depend on concrete HL1. No renderer, physics, wire grammar, launcher,
server cfg, installed game asset or pinned SDK implementation was changed by E18.

## Verification / handoff

Pre-edit Release baseline: 395 cases, 385 passed, 10 explicit optional skips,
271,093 assertions passed (`baseline-release.txt`). Final result:
`python_357_fire_zoom_reload_integrated`; offline verification complete.
Evidence lives in the existing task-record directory
`manual-artifacts/task-records/e18-python-357-20261004/`.

| Final check | Actual result / evidence |
|---|---|
| VS2022 / Win32 normal Debug and Release | Builds passed, including normal hlclient and existing manual helpers; `build-debug-final-world-controls-retry.txt`, `build-release-final.txt`, `build-release-final-controls.txt` |
| Final affected Debug | 417 cases: 406 passed, 11 explicit installed-resource skips; 271,904 assertions passed; `focused-debug-handoff-final.txt` |
| Final Release E18 with approved read-only assets | 22 cases / 1,153 assertions passed, including owned fixtures and actual v/p/w/ammo model plus four WAV controls; `e18-release-assets-final.txt` |
| Full Release offline suite AFTER the last production/test edits | 2,894 CTest entries: 2,850 executed/passed, 44 explicit skips, zero failures, 132.48 s; `release-offline-handoff-final.txt`, `release-handoff-final-LastTest.log` |
| Registered-command audit | Actual JSON has 2,894 bounded offline registrations; capture-script entries select validation-only branches with project-owned fake paths/peers. No stock campaign is invoked. WFP=0, installed-resource/device opt-ins unset; `release-test-inventory.json` |
| Core-only Release | `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`; complete existing build and 41/41 CTest pass; `build-core-release-retry.txt`, `core-only-ctest-final.txt` |
| Architecture / alternate module | Guard allowed + seven negative direct/alias/transitive/generator/source/object/include cases pass. Actual core-only manifest and 147 VS engine/helper projects contain no HL1 source/object/header/link; TestGameClientModule exercises the same neutral projection and bounded edge-input host without HL fallback |
| Scoped ASan build/runtime | Existing Win32 Debug core/API target rebuilt; 41 cases / 2,518 assertions pass with matching x86 runtime on process PATH; `build-asan-core-final.txt`, `asan-core-api-scoped-final.txt` |
| Full application ASan | Not rerun / not verified; historical application exit 1/no output is not upgraded by scoped core success |
| Audio-device capability | Existing short moderate SDL output test: 1 case / 5 assertions passed; `audio-device-debug-final.txt`; not a human listening claim |
| Null / process / transport / no-stock | Existing source-build TestOnly workflow passed version/help, Null single frame without game assets, transport self-test, native/PowerShell summaries and full fake-runner/manual-report/external-map contract checks; no stock processes |
| Launcher | Unchanged script; reference + peer + NoTimeLimit, off, damage-respawn-check and SourceOnly CheckOnly all pass; `launcher-checkonly-final.txt` |
| Preservation / Git | All 1,218 saved files hash-verified, zero removals; only 29 existing files changed for E18 plus seven ordinary new E18 doc/tests/fixtures. Scripts, old E17 tests/docs and SDK unchanged from snapshot. HEAD unchanged, index empty, diff --check passed |

The 44 skips are explicit installed-resource controls or environmental
capabilities (WFP disabled, reparse/symlink/UNC and native SDL focus/capture).
Mandatory owned E18 OpenGL/PCM tests executed; the available installed-Python
and SDL-device controls were separately enabled and passed. Skips are not passes.

Pixel evidence separately covers body 0/1, idle/fire, A1 camera-local poses,
muzzle flash and transient light, remote p357 bone merge/server pose/rearm,
and 90->40->90 at 4:3, 16:9 and 16:10. The world-only GPU test excludes the
viewmodel, requires zoomed world coverage greater than twice baseline and
exact reset pixels, with unchanged resource upload counts. Existing viewmodel
near 1/world near .1 is preserved. Isolated production PCM checks require a
ready nonempty decoded sample, one exact voice, finite nonzero output and no
duplicate voice on replay; material-impact PCM is tested separately from fire.

Initial red/transient diagnostics are retained, not relabeled: parallel core
build hit compiler/MSBuild memory exhaustion; low-parallel isolated retry passed.
The combined GPU control also failed context creation under that load; isolated
GPU subsequently passed. An actual-WAV fixture initially used cue scope 0 in
batch scope 1, so production correctly rejected it; fixing only fixture scope
made all four real WAV checks pass. Win32 fixture ordinal narrowing was likewise
fixed in the new test, not by weakening production validation/old assertions.

Final Release executable: `build/bin/Release/hlclient.exe`, 3,689,472 bytes,
SHA-256 `7755F3BB9939C19642766C00C285FED903AE096AE3F3AEC30542416B0DE4E0CF`.
Launcher still resolves precisely that Release path; its unchanged SHA-256 is
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
No SHA is attributed to an earlier user session without evidence.

Reproducible normal/core-only commands (reuse existing directories, no cleanup):

```powershell
cmake --build build --config Release --parallel 2
pwsh -NoProfile -File scripts/build_source.ps1 -TestOnly -Configuration Release -Parallel 4
cmake -S . -B build-core-g1 -G "Visual Studio 17 2022" -A Win32 -DHLCLIENT_BUILD_GAME_HALFLIFE=OFF
cmake --build build-core-g1 --config Release --parallel 2
ctest --test-dir build-core-g1 -C Release --output-on-failure --parallel 2
```

New live launches / stock client / HLDS / Steam initialization / WFP / managed
captures = 0. Staging, commit, push = none. Other worktrees and installed game
files were not modified. E18 `manual=not_run` for fire, remote, reload, zoom,
zoom reset, MP body variant and kill/respawn until the user tests this build.

## Explicit compatibility limits

- Bright light radius 128/intensity 1 and the inspected p357 barrel-end socket
  are local-compatible presentation profiles, not opaque stock-engine parity.
- Shot variant is deterministic per action, not the SDK's mutable RNG stream.
  Predicted no-echo impact freezes the submitted camera direction; committed-only
  and remote presentation consume available event spread. An echo cannot retrace
  or duplicate an already emitted predicted impact. Damage/spread stay server-owned.
- Existing static BSP impact/material/decal/particle policy is reused; there is
  no new client damage, blood, ricochet probability or dynamic target trace.
- Zoom is server-confirmed only. Original E18 deferred proportional mouse
  sensitivity; the 2026-10-15 corrective below implements the pinned HUD default
  ratio. Crosshair-sprite support was not part of the original E18 change.
- Zero FOV now means horizontal 90 at the actual viewport aspect, deliberately
  replacing the old fixed vertical 60 in the HL module, not changing generic defaults.
- GPU pixels, isolated nonzero PCM and a short SDL output capability test are
  offline evidence, not human listening or new multiplayer acceptance.
- Full application ASan runtime remains unverified; scoped core/API coverage
  is reported separately. No ASan infrastructure repair is part of E18.

Manual command remains:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Check .357 first/last/empty shots, bright flash/light and impact, no brass or
tracer, 2 s conventional reload and server clip, RMB tap/hold/tap zoom,
zoomed fire/reload/switch/death/respawn, remote p357 shot sound/flash without
listener recoil, and E11–E17 regressions. E18 manual remains not_run until the
user tests this executable.

## 2026-10-06 fire-audio cache correction

`attribution=user_report`: .357 shots are silent in both clients. The regression
reproduced an accepted Python shot with an available WAV but a rejected load
after 64 local samples. The shared bounded quota is now 128, covering the current
74-name preparation set. Original 357_shot1/357_shot2 selection and gains,
eligibility, zoom/reload, impacts and no-shell/no-tracer rules remain unchanged.
Local and observer paths each yield one isolated nonzero mixed voice, with no
duplicate replay. Current evidence and new-build manual status:
[E2 shared audio-cache correction](LOCAL_WEAPON_AUDIO_E2.md).

## 2026-10-15 .357/Crossbow zoom-sensitivity corrective

The user reported that .357 and Crossbow zoom did not lower mouse sensitivity.
The production input builder always used the unchanged base
`movement_policy().mouse_look`, while the reconciled FOV affected projection
only. This was the explicitly deferred original E18 behavior above, not a
new wire, mouse-capture or weapon-switch fault.

Read-only reference: the same pinned Valve SDK revision
`b1b5cf5892918535619b2937bb927e46cb097ba1`, `cl_dll/hud.cpp:330,660-669`
and `cl_dll/inputw32.cpp:458`. Default `zoom_sensitivity_ratio` is 1.2; a
non-default FOV multiplies base sensitivity by `FOV / default_FOV * 1.2`.
Default FOV restores the base value without the extra 1.2 multiplier.
The project's Half-Life default remains horizontal 90 degrees: confirmed
.357 FOV40 selects 53.333% of base, and Crossbow FOV20 selects 26.667%.
This is relative zoom-policy compatibility, not a claim that the project's
base degrees/pixel, acceleration or entire stock input path are identical.
The ratio is the pinned default, not a new configurable cvar.

`HalfLifePresentation::camera` publishes an owning neutral
`CameraIntent::mouse_sensitivity_scale` from the same resolved horizontal FOV
used for rendering. It does not re-read raw competing FOV sources, infer a zoom
from RMB, choose a weapon in the engine, or create an interpolated FOV.
Existing fresh-source ordering and reset floors remain the single authority.
Normal/default/invalid/reset projection selects scale1; retained zoom keeps its
current scale. Reload, holster, death/new life and map/session reset therefore
restore projection and input together. Focus loss still clears physical input;
it does not invent a server zoom toggle.

The normal application samples this const intent before building physical
input, composes it into a local copy of the base `MouseLookConfig`, and applies
the neutral multiplier once before the existing per-frame look cap. The base
setting is never mutated or cumulatively scaled. Validation requires a finite
scale in (0,4]; the base sensitivity retains its existing .001..10 bounds, so
valid low-sensitivity zoom can have an effective value below .001. The final
camera pitch clamp remains unchanged. Aspect, frame duration, movement axes,
buttons, command sampling, immutable history and replay do not apply the
multiplier again. A later committed FOV/reset affects the next physical sample,
never an already generated command.

Neutral/default and alternate modules select scale1 unless they explicitly
provide another value. No Half-Life constants or weapon identifiers enter
input, the application camera mechanism, renderer or prediction. Regression
coverage in `test_python_357_camera.cpp`, `test_gameplay_input_intent.cpp` and
`test_game_client_module.cpp` exercises both real module weapon fixtures,
SetFOV/clientdata precedence, retained values, default/invalid/reset/lifetime
boundaries, frame/aspect independence, base preservation, pre-clamp ordering,
capture gates and alternate/core-only forwarding. The independent camera
configuration validator also covers the new neutral scalar.

Pre-edit recovery snapshot:
`manual-artifacts/task-records/e21-zoom-sliders-20261015/source` (1,420
hash-verified project source files). Build/test results are recorded in that
task's verification artifacts after the coordinated builds. Debug and Release
focused regressions passed11/11 with4,545 assertions; core-only CTest passed
44/44. See [combined verification](GAUSS_PRIMARY_CHARGE_BEAM_E21.md#combined-gausszoomlauncher-verification)
for the final full-suite result and ordinary Release hashes.
No live game/server was launched for this change; manual feel remains
`manual=not_run`. The user should compare the same mouse movement unzoomed,
.357 zoomed and Crossbow zoomed, then reload/switch/die/respawn to confirm the
base sensitivity returns.

### 2026-10-15 selected-weapon sensitivity audit

At pinned SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`,
`cl_dll/inputw32.cpp:458` selects HUD sensitivity or the user's base value;
`hud_redraw.cpp:69-78` and `hud.cpp:660-669` derive the override from FOV, not
weapon ID. `hud.cpp:330` registers zoom ratio 1.2. The weapon code selects
FOV40 for Python (`dlls/python.cpp:144-152`) and FOV20 for Crossbow
(`dlls/crossbow.cpp:449-456`). No additional per-weapon sensitivity policy was
found in client/weapon SDK sources. Input acceleration is a separate global
setting, not a weapon modifier.

The preceding zoom fix already implements the required relationship: default
FOV90 gives 100%, Python zoom gives 53.33%, Crossbow zoom gives 26.67%.
No production sensitivity change was necessary. A new real-module regression
checks identical physical yaw/pitch for all 15 selected weapon IDs at default
FOV. Existing zoom/reset/source-order tests remain intact. This does not claim
stock-exact base acceleration or OS input settings.
Evidence: `manual-artifacts/task-records/studio-lighting-sensitivity-20261015`.
