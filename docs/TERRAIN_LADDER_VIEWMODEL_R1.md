# R1 — terrain presentation, reference ladders, weapon transition

This is an offline corrective slice on the existing G1/D4/E2 client. It does
not claim a new live run. The user's Crossfire screenshot suggests a terrain
seam, but gives neither collision plane nor runtime coordinates; that exact
site and cause remain unverified. `attribution=user_report`: E2 sounds work,
the earlier corrected lift is smooth, and HUD, weapons, Use and pickups work.
Those confirmations are not overwritten by the newer terrain report.

## A: grounded seams

The controlled two-plane convex crest kept physical endpoints grounded while
the straight camera interpolation chord entered the collision floor. The old
presentation path snapped to its preceding endpoint on that blocked chord.
`recover_reference_ground_presentation` only handles two world-grounded,
walkable endpoints with the same hull and a blocked walkable-plane chord. It
traces the actual world floor at the intermediate XY, applies at most four
binary32 outward steps to a generated point, and requires a clear raised
sweep. Physical movement, server seed, correction and history are untouched.
The active D4 sampled brush scene is used for the recovery check too; a moving
support is never inferred for world-grounded terrain. Independent fixture
checks cover crest, valley, flat/slope in both directions, a diagonal crossing,
crouch-walk, slower quantized axis, existing step/edge and vertical lift.

## B: ladder command and collision context

The production reference command path previously transmitted numeric
forward/side axes without the corresponding held `IN_FORWARD`, `IN_BACK`,
`IN_MOVELEFT`, `IN_MOVERIGHT` bits. Valve `PM_LadderMove` consumes those bits.
Typed physical held/pressed/released input now carries them through the HL1
policy, immutable command history and reference wire encoder. Opposing keys
retain both physical bits even when their numeric axis cancels; a brief press
is not reconstructed from an axis sign. This intentionally changes live
usercmd button bytes; historic captures and codec fixtures are not rewritten.
The separate synthetic local-controller path excludes directional presses from
its pending one-shot revision: a continuous key edge cannot become a replayed
synthetic action. Live reference input still retains a short physical press
until the next command is inserted into immutable history.

The current committed entity frame retains a brush as a nonblocking ladder
only with `SOLID_NOT`, `MOVETYPE_PUSH`, `skin=CONTENTS_LADDER (-16)` and a
validated `*N` model binding. Exact hull membership and traced entry normal
drive the local reference predictor. It cannot become a blocking wall or
follow a stale render transform. The Half-Life module opts in and supplies
climb speed 200, duck multiplier 0.333, and jump-away speed 270; the neutral
host has no Half-Life default. `MOVETYPE_FLY` is accepted as a seed only after
this ladder context proves contact. Unsupported flight, liquids and trains
remain unsupported. The same simulation path is used for append and rebase;
loss of contact returns to the ordinary air/ground kernel. This is a bounded
reference subset, not a claim of full stock PM_Move parity. The offline
fixture proves up/down/idle/jump-away/contact loss and replay identity; a
stock top-platform handoff still needs manual validation.

## C: Glock transition

The B1 controller retained a pre-action idle visual. At the fire terminal
frame it returned that old idle but forced frame zero, then repeatedly returned
frame zero because the completed action remained active. This reproduced an
extra discontinuity without camera motion. Completion now closes the action
once and creates one fresh idle restart/timeline, unless a newer server-owned
idle event already exists. Fresh authoritative events and correction
precedence are unchanged. No pose blending or new audio playback was added;
E2 marker occurrences remain owned by the original action. Controlled tests
cover fire, both reload variants, repeated timestamps, irregular/30/60/144 Hz
sampling and owned OpenGL frame rendering.

## Verification record

The pre-edit focused Release baseline had 149 passes and one optional-asset
skip. The controlled crest test first failed on three blocked presentation
chords despite grounded physical endpoints, then passed through the bounded
presentation recovery. The physical-direction test first produced zero where
independently expected wire masks were 8/16/512/1024, then passed through
the corrected typed input path. The ladder test first found an absent
ladder context and a failed climb, then passed with a server-marked
non-solid brush and replay identity. The stationary Glock test first found a
reused idle restart and repeatedly returned frame zero after fire; the same
test passed after the single fresh-idle transition.

The broad Release run also exposed two integration regressions: continuous
direction presses were incorrectly staged as synthetic one-shots, and a
fake-live test supplied forward axis 1 while reporting W released. Both were
fixed without relaxing production input validation. The final Release offline
suite passed 2,343 cases and 818,439 assertions with 28 capability/opt-in
skips. Debug focused tests passed 63/64 cases (one optional-asset skip), and
ASan Debug passed 15/16 (the same skip). The G1 core-only API test and
architecture guard passed with the Half-Life target disabled. Fast reference,
Strict off and 50 HP reference launcher `-CheckOnly` all passed without a
runner launch. These are offline results, not a new manual or stock-server
verdict.

`new live/HLDS/Steam/WFP sessions=0`; the user must recheck the real Crossfire
site and actual ladder top/bottom behavior manually. The unmodified launcher
command is:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```
