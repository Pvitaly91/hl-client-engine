# GoldSrc local movement dry-walk subset

M4.6.3.2 adds a pure, fixed-command local movement path:

```text
GameplayInputIntent
    -> synthetic GoldSrcUserCmdState
    -> GoldSrcLocalMovementKernel
       + immutable GoldSrcMovementEnvironment
       + ILocalMovementCollision
    -> immutable LocalPlayerMovementState
    -> player-walk first-person camera
```

The executable compatibility profile is
`public_valve_pm_shared_dry_walk_subset_v1`; its evidence profile is
`public_valve_pm_shared_and_independent_fixtures`. Public Valve
`pm_shared.c`, `pm_defs.h` and the pinned project collision contracts provide
equation and constant evidence. Independent literal fixtures establish the
project implementation. The full original GoldSrc `PM_Move` and original HLDS
prediction compatibility are explicitly not claimed.

## Pure API and command time

`GoldSrcLocalMovementKernel::simulate(previous, command, environment,
collision, scratch, config)` reads no clock, input device, renderer, file or
network object. It accepts only contiguous
`GoldSrcUserCmdCompatibilityProfile::synthetic_usercmd_v1` commands.

Command duration is exactly `command.msec * 0.001` seconds, not render-frame
delta. The default maximum command duration is 255 ms. Duration is divided
into `ceil(duration / 10 ms)` deterministic substeps, capped at 32; equal
substeps are followed by a final remainder so the original total is preserved.

## Environment

`GoldSrcMovementEnvironmentBuilder::from_move_vars` executes `gravity`,
`stop_speed`, `maximum_speed`, `acceleration`, `air_acceleration`, `friction`,
`step_size`, `maximum_velocity` and `entity_gravity`. Water acceleration,
water friction, edge friction, bounce, z maximum and wave height are retained
but not executed. Invalid or pending environments fail closed; values receive
no runtime defaults.

The offline viewer/checker intentionally uses
`project_owned_offline_baseline_v1` (`800, 100, 320, 10, 10, 4, 18, 2000,
1` for the executed fields). Those values are deterministic project fixtures,
not captured server state.

## Per-command operation order

After validating profiles, sequence, duration, revision and collision source,
the kernel performs a stationary hull check and a point-contents query. Each
substep then performs:

1. a bounded downward ground categorization;
2. immediate duck/stand transition on the first substep only;
3. jump press-edge evaluation on the first substep only;
4. grounded friction, ground acceleration, per-axis maximum-velocity clamp,
   slide/step selection, and a final ground probe; or split gravity, air
   acceleration, slide, split gravity and a final ground probe (a grounded
   jump receives the first half through the public `PM_Jump` fixup order);
5. final per-axis maximum-velocity enforcement for every movement path;
6. a post-move contents query.

After all substeps, one validated successor state is created. `old_buttons`,
the exact command sequence, integer command time and revision are advanced
only in that successor.

## Wish movement

Pitch never leaks into movement. Yaw zero faces +X and the horizontal right
vector faces -Y. Forward and side values are combined, normalized, and capped
at the environment maximum speed. On walkable ground the direction is
projected onto the contact plane before acceleration, and the resulting
velocity is projected onto that plane before tracing. `up_move`, light level,
impulse, weapon selection and impact fields are inert in this kernel.

The existing synthetic speed/run bit is also inert here. Command movement
values alone set wish speed; no stock Shift/`+speed` semantics are claimed.

## Ground and unsupported contents

Ground categorization traces the selected hull two units downward unless
upward velocity exceeds 180. A hit is walkable when `normal.z >= 0.7`; the
origin snaps to its trace endpoint and velocity directed into the plane is
clipped. Steeper contacts leave the state airborne.

Water, slime, lava and current categories return
`liquid_movement_unsupported`; no dry-walk successor is published. Ladder
movement has its own pending typed error but no ladder detector is implemented
in this milestone. Start-solid and all-solid traces also fail transactionally;
there is no stuck-position nudge.

## Collision profiles

Production player-walk uses `WorldOnlyMovementCollision` and original BSP
world model zero. `SyntheticBrushMovementCollision` exists only for scenes
whose static-solid role was assigned explicitly. Classnames are never treated
as solidity evidence. Stock doors, moving platforms, conveyors and dynamic
brush transforms remain absent.

## Public constants

| Value | Project literal | Pinned public source | Status |
| --- | ---: | --- | --- |
| standing hull Z | `-36..36` | `pm_shared.c:85-86` | exact literal |
| duck hull Z | `-18..18` | `pm_shared.c:78-79` | exact literal |
| standing/duck eye Z | `28 / 12` | `pm_shared.c:80,87` | exact literal |
| stop epsilon | `0.1` | `pm_shared.c:88,715-741`, `PM_ClipVelocity` | exact literal |
| clip planes | `5` | `pm_defs.h:23`; `pm_shared.c:800,885` | exact literal |
| slide bumps | `4` | `pm_shared.c:809`, `PM_FlyMove` | exact literal |
| ground probe | `2` | `pm_shared.c:1565`, `PM_CatagorizePosition` | exact literal |
| walkable normal Z | `0.7` | `pm_shared.c:1576`, `PM_CatagorizePosition` | exact threshold |
| upward ground cutoff | `180` | `pm_shared.c:1567`, `PM_CatagorizePosition` | exact literal |
| air add-speed cap | `30` | `pm_shared.c:1292`, `PM_AirAccelerate` | exact literal |
| jump impulse | `sqrt(2*800*45)` | `pm_shared.c:2596,2601`, `PM_Jump` | independently calculated |
| gravity split | half before/after; grounded jump applies its first half inside `PM_Jump` | `pm_shared.c:747-786,2588-2605,3183-3189`, `PM_AddCorrectGravity` / `PM_Jump` / `PM_FixupGravityVelocity` | equation/order evidence |

Immediate duck timing, horizontal-only friction, offline MoveVars, substep
limits and airborne center-preserving duck are explicit project-owned policies.

See [friction and acceleration](GOLDSRC_MOVEMENT_FRICTION_ACCELERATION.md),
[slide and step](GOLDSRC_MOVEMENT_SLIDE_STEP.md), and
[duck and jump](GOLDSRC_MOVEMENT_DUCK_JUMP.md).
