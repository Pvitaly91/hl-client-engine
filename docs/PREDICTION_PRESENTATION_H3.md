# H3 reference prediction presentation

The live reference predictor retains the 20 ms command scheduler and the
immutable wire commands used by transmission. Server records still correct the
matched command boundary before replay; the renderer reads the resulting
history without changing simulation, command generation or canonical state.

## Reproduced presentation defect

The H2 implementation cleared its previous/latest presentation pair after
every accepted rebase and set the latest time to server receipt `now`. A new
record arriving halfway through a command interval therefore made the camera
show the later predicted endpoint immediately. The production LiveRuntimeStage
regression measured a 0.239997864-unit jump on an exact correction at the same
render timestamp. This was independent of a large physics error.

H3 chooses two adjacent states from the **corrected** prediction history. It
uses their local scheduler command times to select the pair and bounded alpha
for `render_time - 20 ms`. It never combines an old pre-correction endpoint with
a corrected one. When the corrected history has no usable pair, it returns a
bounded endpoint with an explicit reason. A collision trace validates an
interpolated camera candidate. Query scratch belongs to the live stage and
retains capacity; same-time requests against the same history/world revision
reuse one snapshot. Rebase publication epochs remain separate from the
session/map/collision identity checks used by history and simulation.

The exact-correction regression now measures 0.0-unit camera jump at the same
timestamp. A catch-up update verifies distinct 20 ms pair times, and 30, 60,
144 FPS plus uneven render requests leave command count, predicted endpoint and
canonical publication unchanged. These are offline temporal results; the
user's physical assessment of the new build remains pending.

The first H3 managed run found a second temporal case: a carrier anchor at the
newest local command can leave no corrected state before the requested render
time. The old endpoint fallback moved the camera 2.315885 units on a correction
whose maximum raw matched-boundary error was 0.032966. A new production test
reproduced a 0.870002747-unit jump with a newest-command anchor and a raw
error below 0.1. The camera-only bounded visual correction layer now handles
this specific short history gap. Its duration is 20 ms and maximum residual
is 4 units; it runs only for a valid same-mode/hull correction with raw error
at most 0.25 unit. Point and player-hull traces validate the presented result.
The deterministic jump is 0.0 at publication. A repeated correction does not
restart the fade deadline. Larger or incompatible corrections still snap or
fall back under the existing contract.

## Managed H3 checks

The first reference run (`31fac4db82294243b3207f4fabeccec1`) exposed the
newest-anchor gap: maximum camera correction-boundary jump 2.315885 units
versus maximum raw correction 0.032966. This observation led to the second
production regression and bounded camera-only fix above.

The final Release binary ran in the second and last permitted H3 session
(`499124e891254e6a817449385be21af2`). The native and managed prediction
result was `live_local_prediction_and_reconciliation_verified`; app exit was
zero, owned cleanup and restoration were exact. It received 120 committed
server samples, advanced 272 local steps, accepted 98 corrections and replayed
59 commands. Of 712 active presentation frames, 626 interpolated, 82 used an
endpoint and 4 applied the bounded camera correction. There were no
collision-blocked or long-stall frames. The query scratch grew once. Maximum
camera jump at a correction boundary was 0.519404, equal to maximum raw
matched-boundary error 0.519404. The first run's large visual-only jump was
not observed.

The second scripted movement window was not suitable for a walking smoothness
comparison: only 4 of 34 fresh moving server samples changed position. The
typed normal and Shift commands were transmitted at encoded magnitudes 400
and 120, while the speed evaluator returned `server_motion_unverified`.
Physical smoothness for this new build remains pending user assessment. The
H3 managed budget is exhausted at 2/2.

## Scope and manual history

The predictor covers world-only dry walk/air movement. Jump and duck buttons
remain on the wire, but their physics may use server-sample fallback. Map,
hull, movement-mode or unsupported collision discontinuities retain explicit
fallback. No additional command sends or physics steps occur during rendering.

User report (`attribution=user_report`): W/S, A/D, mouse, Space, Ctrl, Shift
passed as controls; reference felt smoother than off; reference smoothness
still needs work. This does not assert predicted jump/duck or fully smooth
physical movement.

Repeat manual check after the final Release build:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```
