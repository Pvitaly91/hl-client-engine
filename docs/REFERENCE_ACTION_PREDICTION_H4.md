# H4 reference action prediction

The receiving client keeps the H3 20 ms scheduler, immutable quantized wire
commands, sent-carrier anchor, corrected history, command-time presentation,
and collision-checked camera correction. H4 adds a versioned
`reference_carrier_jump_duck_v2` / `reference_wire_jump_duck_v2` path. The v1
dry-walk profile and synthetic profile retain their own contracts. Only the
`IN_JUMP` and `IN_DUCK` bits are admitted by the new wire adapter; unsupported
fields and contexts remain explicit fallbacks. Action simulation consumes a
committed command once. Backup sends, replay, and render samples do not sample
keys or generate commands.

## Reproduced restriction and state contract

The v1 adapter rejected every nonzero `wire.buttons`. The v1 seed also treated
`bInDuck` as equivalent to `usehull==1`; an ordinary completed crouch has
`FL_DUCKING` and duck hull while `bInDuck` is false. The H4 production-stage
regression verifies active history across jump, correction, and airborne duck.
The kernel regression verifies the 400 ms ground duck transition, stable held
crouch walking, blocked unduck, and subsequent standing recovery.

The v2 immutable state carries previous buttons, remaining `flDuckTime` in
milliseconds, transition activity, hull, view offset, ground and velocity at
each command boundary. A server record supplies observed origin, velocity,
flags, view offset and hull. A matching retained history slot can supply
previous buttons and missing transition fields; an unrelated latest state
cannot. A missing timer during a transition remains a seed failure. Completed
crouch and `bInDuck` are separate fields. Reconciliation compares the raw
matched-boundary state before visual correction, replaces only the accepted
anchor, and replays its ordered suffix without input or network side effects.

The shared kernel runs each reference command once. It applies button edge
logic, grounded jump impulse and gravity, the Valve duck eye trajectory,
ground origin/hull compensation, immediate airborne duck completion, stable
crouch speed scaling on a simulation-local movement value, and an occupancy
test before unduck. The transmitted values are unchanged. Jump and crouch
remain under world-only dry movement; water, ladders, moving platforms,
long-jump module and special modes remain fallbacks.

The presentation sample uses corrected adjacent command states and separate
origin/view-offset values. Airborne and stable crouch pairs sample each render
time. At a hull transition it validates the eye segment and selects a valid
hull endpoint; it never interpolates a physical hull or runs physics from the
presented camera. A blocked segment has an explicit collision reason.

## Pinned semantic references

- Valve SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`:
  `pm_shared/pm_shared.c` (`PM_ReduceTimers`, `PM_Duck`, `PM_UnDuck`,
  `PM_Jump`, `PM_PlayerMove`), `pm_shared/pm_defs.h`,
  `common/in_buttons.h`, `common/const.h`, `network/delta.lst`,
  `cl_dll/entity.cpp`.
- ReHLDS `6266cd23faee4a6e9cf3974f9605b2cadd86f0a4`:
  `rehlds/engine/sv_user.cpp` for the server command path.
- Xash3D `7500a6b3647e71d9b21691671957a0e06731019e`:
  `engine/client/dll_int/cl_pmove.c` for local command prediction/state
  transfer semantics.

These sources define behavior and wire/state meaning; the H4 implementation
and tests use project-owned code and fixtures. The live managed result and a
new physical smoothness assessment are recorded separately in the H4 task
record. Historical H3 standing-walk smoothness remains user-confirmed.

## Live boundary

The second and final H4 managed attempt reached the owned server and client,
sent 460 new commands, received 201 fresh samples, and kept reference prediction
active through jump, landing, crouch walking and standing recovery. It counted
168 corrections and 112 replayed commands. Both predicted and fresh server
states moved horizontally while crouched. The application nevertheless exited
2: the inherited H3 acceptance window did not classify scripted jump/duck
movement, leaving `moving_active_frames=0`. The native H4 result and wrapper
therefore stayed partial. The final code counts the actual H4 moving phases;
its Debug, Release, ASan and parser regressions pass, but the managed budget is
exhausted and this final binary has no further live verification.

The live maximum raw matched-boundary position error was 0.036183 unit. The
run's 18.079712-unit `maximum_camera_correction_jump` used the old origin-only
metric and could count the physical -18-unit duck origin change as a camera
jump. The final metric uses presented **eye** (`origin + view_offset`). A
stationary literal-world test confirms the -18 origin transition with less
than 1 unit of eye movement. The user's assessment of H4 jump/duck/crouch
smoothness remains pending.
