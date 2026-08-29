# Player wall-contact stability

M4.6.3.2.1 is the stabilization boundary for sustained `player-walk`
contact. It does not add prediction, replay, reconciliation, network traffic,
stock `PM_Move`, dynamic brush collision, or stuck recovery.

M4.6.3.3 subsequently reuses this unchanged movement invariant during local
prediction and replay. Its separate prediction checker/viewer must retain the
same collision-valid publication, bounded zero-progress handling, and
wall-contact stress gates; no reconciliation path may bypass them. This is
synthetic authority only and adds no network traffic. See
[prediction reconciliation](PREDICTION_RECONCILIATION.md).

The manual prerequisite for M4.6.3.3 is recorded as accepted: after the
M4.6.3.2.1 fix, the user confirmed that `player-walk` no longer exits on wall
contact and that both direct and diagonal wall contact remain stable. The
prediction milestone retains that observation as a regression prerequisite;
it does not reinterpret it as stock movement or network evidence.

## Accepted symptom and baseline classification

The accepted report was an abruptly disappearing Debug viewer while W/A/S/D
was held into walls on `maps/crossfire.bsp`. Before source changes, clean
Win32 Debug and Release viewer targets were built and the exact reported route
was run three times. Both Debug attempts returned process exit code `1` with
the typed controller category `scheduler_failed`; the Release attempt returned
code `1` with `movement_simulation_failed`. No Windows exception code, access
violation, abort, or `std::terminate` was observed. The visible symptom was
therefore a clean nonzero typed exit that destroyed the SDL/OpenGL window.

The movement-specific deterministic reproducer is an oblique BSP plane whose
exact double-precision crossing narrows a component to binary32 on the solid
side. The previous trace contract published that coordinate as the successor
origin. A ground probe, later slide bump, or next command could consequently
report `player_startsolid` or `player_allsolid`. Existing coverage used exactly
representable axial planes or intentionally stopped after the first boundary
command, so it did not exercise binary32 boundary re-entry.

## Contact publication policy

A collision candidate at the requested endpoint remains a collision even when
its geometric fraction is exactly one. No-hit is represented by absent contact
metadata, not by `fraction == 1` alone.

Movement begins each operation at a position already verified free. A traced
contact endpoint is staged and tested through the same standing or duck hull
before publication:

- a free materialized endpoint may commit and consumes its measured fraction;
- a blocking binary32 endpoint never commits; the prior verified-free origin
  remains authoritative and effective positional progress is zero;
- the collision plane is still clipped and recorded, so glancing motion may
  continue tangentially without a coordinate nudge;
- `startsolid` and `allsolid` at the verified starting origin remain typed
  errors and are not hidden.

This is not an arbitrary position nudge, `nextafter` workaround, or global
epsilon increase. The invariant is that every published successor origin has
passed the production collision provider's full position test.

## Zero progress and retained planes

Slide movement uses bounded double-precision remaining time. Every bump must
provide origin/fraction progress, a useful finite velocity change, or a
successful stable-stop decision. A zero or near-zero fraction cannot spin:

- same-facing near-coplanar planes are deduplicated before capacity checks;
- duplicates reuse the retained plane and do not increase distinct-plane
  statistics;
- opposing near-coplanar planes represent a trap and stop blocked velocity
  successfully;
- genuinely distinct planes keep deterministic encounter order;
- only a genuinely distinct plane beyond `maximum_clip_planes` returns
  `clip_plane_limit_exceeded`;
- a repeated no-progress contact returns a bounded successful stop while
  preserving a valid tangential component where one exists.

Touch storage is limited by `maximum_touches_per_command`. Equivalent touches
within one command may be coalesced only when hit identity, movement phase and
bounded plane equivalence agree. Capacity and allocation failures are typed,
publish no successor state, and do not run inside an incorrectly declared
`noexcept` boundary.

## Step and scratch transactionality

Direct and step candidates own separate origins, velocities, touches and
statistics deltas. Only the selected candidate commits. A geometrically
unavailable optional step cannot invalidate an already valid direct slide.
Statistics aggregation is staged and assigned atomically after every checked
counter/distance addition succeeds.

`CollisionQueryScratch` is caller-owned and bounded. Query-scope cleanup resets
active node/clipnode marks on success, no-hit, typed failure and C++ exception
unwinding. No candidate retains a reference into another candidate's storage.

## Interactive failure policy and diagnostics

`--movement-diagnostics off|summary` defaults to `off`. Summary mode records
only bounded metadata: ordinals, signatures, phases, counters, fraction and
finite/plane classifications, typed result categories, and camera/visibility
revisions. It does not print coordinates, velocities, BSP bytes, raw traces,
or input recordings. The caller-owned overwrite ring allocates no storage in
the normal frame loop.

An unexpected typed controller/movement failure is fatal to further movement,
but not to an otherwise healthy renderer. The viewer:

1. retains the last valid player state and camera;
2. latches movement simulation off;
3. clears held and pending input;
4. requests relative-mouse capture release;
5. prints one bounded failure summary;
6. continues event polling, scene rendering and swapping until the user closes
   the window;
7. ultimately returns nonzero, so the failure is not disguised as success.

The top-level C++ boundary classifies `std::bad_alloc`, standard exceptions and
unknown C++ exceptions. It does not install a process-wide SEH handler and does
not hide access violations, heap corruption, or stack overflow.

## Automated coverage

`test_player_wall_contact_stability.cpp` covers direct, glancing, parallel,
diagonal, corner/trap, jump, duck/stand, high-speed, zero-progress and repeated
plane contacts, including a 10,000-command sustained campaign. The movement
checker adds these real-map scenarios:

- `wall-contact-stress`
- `wall-glance-stress`
- `corner-contact-stress`
- `jump-wall-stress`
- `duck-wall-stress`

Wall selection uses bounded radial standing-hull traces. A usable full-height
vertical plane is selected by shortest distance, fixed direction ordinal and
source plane index. The trace-proven radial direction reaches the selected
finite BSP face; after the first exact contact, a bounded friction-aware command
removes only residual tangential approach velocity and sustained pressure uses
the wall's inward normal. Release is then proven by an exact same-hit,
same-source-plane inward probe more than one unit from the face before exact
recontact is counted. Duck stress discovers and verifies the corresponding
standing and duck hull faces independently because their source plane indices
may legitimately differ. Commands are generated on demand; no unbounded
command or diagnostic history is retained. Each route runs twice and must
produce matching selection, route and final-state hashes. Scenario-specific
gates additionally require positive and negative glancing progress, airborne
wall touches after a real jump, and ducked plus restored-standing wall touches.

The scripted viewer's selected-wall counter is evaluated only against touches
returned by commands that the controller commits. Provider traces from a
discarded direct or step candidate cannot satisfy the OpenGL wall-contact
proof.

An OpenGL startup skip is accepted only after a typed context/function/version
failure or an SDL diagnostic that explicitly says video/OpenGL capability is
unavailable or unsupported. Generic SDL initialization/window/context errors,
allocation/resource exhaustion, shader/upload/draw/swap failures, and
programming errors remain nonzero failures.

The opt-in `HLCLIENT_ENABLE_ADDRESS_SANITIZER` CMake option first checks the
selected MSVC capability. It is disabled by default and instruments only the
movement/collision-focused libraries, checker, viewer and tests. Unsupported
toolchains fail configuration explicitly rather than silently pretending that
ASan ran.

## Manual acceptance

Build:

```powershell
cmake --build build --config Debug --target `
  hlclient_world_viewer hlclient_movement_check `
  hlclient_prediction_viewer hlclient_prediction_check
```

Run the accepted `crossfire` route:

```powershell
.\build\bin\Debug\hlclient_world_viewer.exe `
  --basedir "D:\Steam\steamapps\common\Half-Life" `
  --game valve `
  --map maps/crossfire.bsp `
  --camera player-walk `
  --visibility pvs-frustum `
  --brush-submodels static `
  --cull back `
  --movement-diagnostics summary
```

Test at least five minutes: direct and 5–15 degree diagonal contact, wall
sliding, convex corners, jump-wall, duck/stand near a wall or ceiling, and at
least twenty release/recontact cycles. Repeat for `maps/boot_camp.bsp` and
`maps/stalkyard.bsp` for at least two minutes each. Normal close returns zero;
any latched typed movement failure keeps the window responsive but returns
nonzero after close.

Raw local diagnostics belong only in the gitignored
`manual-artifacts/player-wall-contact/` directory. If a failure recurs, retain
only the exit/exception category and code, last bounded summary, map, contact
scenario, Debug/Release configuration, and debugger/ASan status. Do not share
BSP/WAD files, dumps, raw traces, coordinates, or private game assets.
