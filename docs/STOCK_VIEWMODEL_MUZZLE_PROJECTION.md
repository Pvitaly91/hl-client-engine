# Stock Half-Life viewmodel and muzzle projection (2026-10-17)

## Cause and implementation

The normal Half-Life module previously published FOV 90 as a horizontal angle
at the **actual** viewport aspect. Consequently its vertical coverage narrowed
from 73.74 degrees at 4:3 to 58.72 degrees at 16:9: meshes and muzzle sprites
became 4/3 as large in screen-height units. The inspected approved installation
has `default_fov 90`, `gl_widescreen_yfov 1`, and `viewsize 120`. Valve's
[25th anniversary notes](https://www.half-life.com/en/halflife/25th) identify
widescreen FOV as an original-game graphics option. Matching that selected
default is the target here, not legacy `gl_widescreen_yfov 0`.

The module now requests a neutral 4:3 FOV reference aspect. The existing camera
adapter preserves that vertical angle as the viewport changes. World geometry,
camera-local Studio models, local muzzle sprites and remote world sprites use
the same projection: no independent weapon FOV, stretch or post-render offset.
At 16:9 the nominal 90-degree setting therefore covers 106.26 horizontal degrees;
the projected muzzle diameter is 75% of its previous screen-height size.
At 4:3 projection is unchanged. Confirmed zoom/reset uses the same conversion;
the existing sensitivity multiplier, authoritative aim and prediction are not
modified. Other modules retain actual-viewport horizontal FOV unless they
explicitly request a reference aspect.

The previous held model transform was identity. The pinned official SDK
`cl_dll/view.cpp`, `V_CalcNormalRefdef` lines 646–687, places a full-viewsize held
origin one source unit below the eye. Half-Life now requests that baseline
camera-local translation for **all 14 weapons and the Satchel radio**. Generic
materialization applies it once to the instance, current attachment and
marker-frame attachment. Source meshes and bone animation remain scale 1;
flash/light and tracer socket positions follow the changed held origin.
Invalid/non-finite or greater-than-64-unit translations are rejected.

This is a baseline framing/projection correction, not a new implementation of
the SDK's complete movement bob, water offset, viewsize cvar, idle sway or punch
separation. The supplied screenshots have different poses, world viewpoints and
resolutions; they do not establish pixel-for-pixel animation-frame parity.

## Muzzle source-scale verification

Do not compensate for the projection error with arbitrary per-weapon scale
halves. Retain the installed 64x64 additive SPR extents and existing mappings:
Glock option 11 / .1, MP5 option 20 / .2, Python and single Shotgun option 31 /
.3, double Shotgun option 41 / .4. Both first-person and observer paths share
these profiles. Weapons without a conventional muzzle flash acquire none.

The official SDK at `b1b5cf5892918535619b2937bb927e46cb097ba1`,
`cl_dll/entity.cpp` lines 339–364, forwards the integer option and evaluated
attachment to the engine callback. It does not contain its implementation.
Separately recorded public reference:
[FWGS muzzle callback](https://github.com/FWGS/xash3d-fwgs/blob/00d9514ca6f3b5085e85dd6d78e81ffe590a9740/engine/client/cl_tent.c#L790)
uses the tens digit in tenths for source scale and the units digit for sprite
selection. This supports retaining the existing scales; no implementation body
was copied, linked or introduced as a dependency. This reference is not proof
of original binary RNG/timing parity. Existing 75-ms cue/light lifetimes and
audio/action gates were not retuned in this size-only correction.

## Verification

Evidence directory:
`manual-artifacts/task-records/viewmodel-muzzle-stock-20261017/`.
Existing dirty E33/E33.1/E33.2 changes are preserved. No Git staging, commits or
pushes, stock-file modification, live client/server launch or capture.

Tests cover neutral legacy/reference projection policies, zoom FOVs 90/40/20,
4:3/16:10/16:9/21:9, atomic invalid-policy rejection, all weapon origin policies,
one-time geometry/socket translation, invalid origins and unchanged source
mesh scale. Opt-in approved-resource OpenGL controls draw every held model and
all four muzzle profiles through the production materializer and renderer.

Completed checks:

- Normal Release client, focused driver, full Catch driver and core/API driver:
  build passed with the existing normal warnings-as-errors policy.
- Focused camera/framing/near-clipping controls with approved original assets:
  19 cases / 4,117 assertions passed.
- Broader first-person, muzzle, weapon visuals, Shotgun and .357/Crossbow zoom
  controls: 135 cases / 25,169 assertions passed, no skips.
- Independent Half-Life-disabled core/API build and run: 445 cases, 444 passed,
  one optional installed-sky case skipped; 409,290 assertions passed.
- Source SPR scales verified separately; all 15 held model variants rendered
  through actual OpenGL. The corrected contact sheet and Glock before/after
  frames were visually inspected. These are offline clear-background controls,
  not a live stock/client pixel-parity comparison.

Old exact zero-origin and actual-aspect HL zoom assertions were updated to
assert -1 origin, scale 1 and explicit 4:3 reference coverage. They were not
removed or replaced with permissive tolerances. Intermediate old-expectation
and premature old-binary attempts are not final acceptance evidence; the
final weapon run is `weapon-regressions-final.log`.

Final client SHA-256:
`AB306AD9CC864D5872D52AE934CB19AC18E87F7C83D2EEB4DE39DD0C5EC8C5F8`.
The launcher is unchanged; the updated executable remains the normal
`build/bin/Release/hlclient.exe`. Binary/source manifests accompany the logs.

Full normal Release offline acceptance: **3,768 passed / 72 skipped / 0 failed**
out of 3,840 registered CTest entries (411.33 seconds). Optional asset/capability
skips remain skips, not passes. Subsequent version/help/Null startup, synthetic
transport, report/control wrappers and SourceOnly/CheckOnly checks passed;
`scripts/build_source.ps1 -Configuration Release -TestOnly -Parallel 2` exited 0.
The complete evidence is `full-release-offline.log` and `LastTest-release.log`.

New gameplay visual acceptance remains `manual=not_run` until the user compares
the updated client. No Debug/ASan runtime claim is made for this correction.
