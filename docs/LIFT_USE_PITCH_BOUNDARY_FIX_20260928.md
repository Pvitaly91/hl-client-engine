# Lift Use: geometric camera / GoldSrc pitch boundary

Worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`; branch
`codex/stock-runtime-campaign-5e48b7c1`; starting HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66`.
Existing dirty work is preserved. Pre-edit snapshot and offline baseline:
`manual-artifacts/task-records/lift-use-pitch-fix-20260928`.

## Additive manual evidence (not validation of this correction)

`attribution=user_report`: the repaired launcher works, the screenshot shows
50 HP, two tested lifts work but the lift next to the shotgun does not react
to E. A subsequent screenshot and report confirm the player is directly
against the same button and still cannot activate it. Historical reports
with `manual_validation=not_run` have not been rewritten.

Run `80de858194d54c47ad9704916f1b430c` ended at 2026-09-28 15:38:51 local;
the screenshot file was created at 15:38:53. It used Release, Crossfire,
reference prediction, Fast validation and the 50 HP profile; server PID
30816, client PID 2316. The owned run reports initial HP 50, later HP 80,
client exit 0, exact process cleanup, scoped exact restoration and zero
wrapper cleanup errors. This is not a crash/launcher repair.

The terminal state places the player on live ground entity 59, brush model
34 (map `lift4`), at (-835.640625, 1311.968750, -1659.968750), with eye
Z -1631.973145, camera yaw 84.3 and pitch -41.3. It reports 13 Use press/release
pairs and 62 new Use-bearing transmitted commands; no brush motion changes.
These are aggregate/terminal diagnostics, not per-press server acknowledgments.
Read-only inspection of the complete map entity bodies also confirms this
button has `health=0`, `spawnflags=1`, no master, and `target=lift4`, matching
the other lift buttons' activation settings (not a shoot-only button).

The earlier run `e1a219ad5df7449391ddbacbe8346ea1` ended farther from the button.
Its terminal distance did not establish the distance of its E attempts and
does not explain the later close-up failure.

## Cause and correction

The geometric camera contract is upward-positive pitch (`forward.z=sin(p)`),
whereas GoldSrc AngleVectors is downward-positive (`forward.z=-sin(p)`).
The production adapter previously quantized the geometric pitch unchanged.
Consequently the world camera aimed down while the server received an upward
aim. Local Valve SDK `pm_shared/pm_math.c` and `dlls/player.cpp::PlayerUse`
provide the independently reviewed convention and Use cone predicate.

For the retained close-up state and stationary button bounds, the server-facing
dot product is approximately 0.373 before conversion, below the SDK threshold
0.7; converting pitch yields approximately 0.964. This calculation supports
the diagnosed defect but is not an execution trace of the stock server.

- `src/goldsrc/usercmd_input_adapter.cpp::build_reference_wire` negates pitch
  once before quantization/history. Both reference/off live modes use this
  same command path. Yaw, mouse controls, buttons, duration, cadence, codec
  grammar and immutable backup/replay rules are unchanged.
- `src/app/live_visual_control.cpp` normalizes raw server `svc_setangle` turns
  before negating pitch into the camera convention. This includes unsigned
  turns such as 350 degrees, applied once by existing source identity.
- Raw protocol observations, provenance, canonical hashing and decoder output
  are unchanged. Server punch conversion was already correct and is untouched.
- The explicitly synthetic input adapter retains its project-only angle
  convention; it is not the production GoldSrc wire path.
- Renderer/viewmodel transforms, game rules, map bytes, lift activation logic,
  collision and the movement kernel are unchanged. This is a shared protocol
  coordinate conversion, not Half-Life gameplay moved into the renderer.

Nonzero production wire pitch intentionally changes. Zero-pitch wire fixtures
must remain byte-identical. Existing dry-walk physics uses yaw for movement;
prediction stores/replays already wire-native pitch without another inversion.
This also corrects the transmitted vertical aim used for attacks; it does not
introduce local hit/damage authority.

## Verification

Baseline Release: 48 focused tests / 2906 assertions passed. New tests compiled
against old production code: 4 cases failed, 43 failed assertions (488 total).
Independent cases cover above/below-horizon rays across five yaws, a project-owned
nearby button cone through G1/Netchan, exact +/-45 degree wire values, retained
Use backup/release and replay, and unsigned/signed server-angle application.
No proprietary BSP or installed Half-Life is required by these regressions.

Post-fix results:

- Release client and tests built incrementally. An initial test build and the
  first ASan build exhausted compiler/MSBuild heap; no source/test expectations
  were changed for that failure. Release completed with one compiler process
  (`MultiProcMaxCount=1`, `CL_MPCount=1`, x64 host tools), without cleaning.
- Release pitch boundary: 4 cases / 488 assertions passed (same tests as red).
- Release focused Use/input/G1/B1/C/prediction/movement suite: 289 passed,
  277346 assertions; one opt-in local Valve model control skipped because no
  local asset environment override was supplied. No fixture expectations were
  mass-updated: the old incoming-pitch assertion changes +12 to -12 explicitly.
- Actual-context Release OpenGL first-person/Studio/HUD fixtures: 3 cases /
  356 assertions passed, no skips.
- Existing `build-core-g1`, Half-Life OFF: incremental build and all 95 core/API
  cases / 1424 assertions passed; architecture guard passed. Actual core project
  and compiler input files contain no concrete Half-Life module references.
- Release Null process replay basic/budget-one/visual fixtures: 3/3 passed,
  original canonical hash expectations unchanged.
- Launcher `-CheckOnly` from System32 passed reference+50 HP, off, and
  reference + `Scenario damage-respawn-check`. No launcher parameters changed.

Normal Debug client/core/API/test targets also built successfully. The same
focused Debug and ASan Debug suites each passed 289 cases / 277346 assertions,
with only the same opt-in local-model skip. The bounded ASan retry built
successfully and the tests reported no sanitizer errors. The selected MSVC
14.34 ASan runtime was added only to the test process PATH and then restored;
no global environment changes. No blanket CTest/live-script suite was run.

Final `git diff --check` passed; snapshot hashes verified, index empty,
branch/HEAD unchanged. The only newly added tracked-source candidate from
this correction is this report; seven already-existing source/header/test
files were narrowly edited. Existing unrelated changes remain untouched.
Result: `goldsrc_view_pitch_boundary_fixed_offline_verified`.

Release `build/bin/Release/hlclient.exe` SHA-256:
`27729EDA708D7E1308BCC537EFBC02738A359D2E8A2E8B573FC07A01F9952210`.
Release orchestrator unchanged:
`76D50F9C33D1164CE43476C6421E476BE7FACDEC55994019995F82DC90CBAF10`.
Research `valve/liblist.gam` unchanged:
`0D4034EBAABCC54AD9F00BB89E9677D5F34C3D3BEDA9185A69D5AC719E0E2FFB`.

New agent live sessions/Steam/WFP/capture/ETW: 0/not_run. No cleanup of old
builds, research-file changes, staging, commit or push. This corrected build
still needs the user's manual validation.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -TestStartHealth 50
```
