# GoldSrc movement duck and jump

M4.6.3.2 supports immediate bounded hull transitions and synthetic jump
press edges. It does not implement the stock timed duck state machine,
animation, long jump, water jump or server prediction.

## Hulls and view offsets

| Mode | Collision hull | Extents | View offset |
| --- | --- | --- | ---: |
| standing | `standing_32x32x72` / hull 1 | `(-16,-16,-36)..(16,16,36)` | Z `28` |
| ducked | `duck_32x32x36` / hull 3 | `(-16,-16,-18)..(16,16,18)` | Z `12` |

Hull 0 and hull 2 are never selected automatically for local movement.

## Grounded duck and stand

On a grounded standing-to-duck transition, the candidate origin moves down by
18 units. On grounded duck-to-stand it moves up by 18 units. Both adjustments
come from `current_min_z - requested_min_z`, so the same foot plane is
preserved.

The destination hull is tested before publication. A blocked duck destination
is a typed transition failure. A blocked stand attempt is deliberately
non-fatal: the player remains fully ducked, origin/view offset remain unchanged,
and `stand_blocked_count` increments. A clear destination publishes the new
hull, origin and view offset atomically.

While airborne, the project policy is
`preserve_hull_center_v1`: the origin does not shift when changing hull, and
the destination hull must still be free. This is explicitly project-owned,
not a stock timing claim.

## Jump edge

Jump is evaluated once, on the first substep of a command, and requires:

- a currently walkable grounded state;
- named synthetic jump bit `kSyntheticGoldSrcButtonJump` held now;
- that bit absent from the previous published state's `old_buttons`.

The upward velocity is assigned, not added:

```text
jump_velocity = sqrt(2 * 800 * 45)
              = 268.32815729997475
```

This relation is independently calculated from the literals used by public
Valve `PM_Jump` (`pm_shared.c:2596,2601`). It intentionally does not substitute
the current MoveVars gravity into the jump formula and does not retain an
existing vertical velocity. Public `PM_Jump` immediately applies
`PM_FixupGravityVelocity`, so the movement trace uses the impulse minus one
half step; the ordinary post-move fixup supplies the second half step.

Because every successful successor records the current button mask as
`old_buttons`, holding jump does not auto-repeat after landing. The button must
be released in a published command and pressed again.

## Gravity, ceiling and landing

Effective gravity is:

```text
environment.gravity * state.gravity_multiplier * environment.entity_gravity
```

The airborne path applies half before acceleration/slide and half after slide;
for a grounded jump, the first half is the explicit `PM_Jump` fixup described
above. Per-axis maximum-velocity checks apply to grounded and airborne
movement. A ceiling collision clips the upward
component through the ordinary multi-plane solver. After the second half step,
ground categorization traces two units down; a walkable hit snaps the origin,
clips velocity directed into the ground and publishes walking state. Steep
planes remain airborne.

Fall damage, landing sounds, footsteps, water jumps, ladders and base velocity
are absent.
