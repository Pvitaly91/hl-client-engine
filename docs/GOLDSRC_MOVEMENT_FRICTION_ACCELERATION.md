# GoldSrc movement friction and acceleration

The movement math helpers are pure, finite-input functions used by the
`public_valve_pm_shared_dry_walk_subset_v1` profile. Their equations are
independently implemented from the pinned public Valve `PM_Friction`,
`PM_Accelerate` and `PM_AirAccelerate` routines. They do not compile or call
the SDK implementation.

## Horizontal wish direction

For yaw angle `a`, forward command `f` and side command `s`:

```text
wish.x = cos(a) * f + sin(a) * s
wish.y = sin(a) * f - cos(a) * s
wish.z = 0
```

The vector is normalized when nonzero. `wish_speed` is its original length
capped by `environment.maximum_speed`; `uncapped_speed` is retained only as
math result metadata. Pitch and `up_move` are ignored.

## Ground friction

The project dry-walk subset deliberately applies friction to horizontal speed
only:

```text
speed   = hypot(velocity.x, velocity.y)
control = max(speed, stop_speed)
drop    = control * friction * player_friction_multiplier * dt
new     = max(speed - drop, 0)
scale   = new / speed
```

Only X/Y are scaled; Z is preserved. When horizontal speed is below `0.1`, X/Y
are set to zero. A zero duration, friction or multiplier otherwise leaves
velocity unchanged. This is a
project-owned dry-walk narrowing: public Valve `PM_Friction` measures full
3D speed and contains edge-friction probing. Edge friction is retained in the
environment but not executed, so this code must not be described as an exact
full `PM_Friction` reproduction.

## Ground acceleration

Let `current = dot(velocity, wish_direction)` and
`add = wish_speed - current`. If `add <= 0`, velocity is unchanged. Otherwise:

```text
accel_speed = acceleration * wish_speed * dt * friction_multiplier
accel_speed = min(accel_speed, add)
velocity += wish_direction * accel_speed
```

On a walkable slope, the kernel first removes the wish component along the
ground normal and renormalizes the result. Maximum wish speed remains the
environment value. After acceleration, it projects the resulting velocity
onto the same ground plane before the bounded velocity clamp and slide/step
candidate traces.

## Air acceleration

Air acceleration retains the public Valve asymmetry:

```text
capped = min(wish_speed, 30)
add = capped - dot(velocity, wish_direction)
accel_speed = air_acceleration * wish_speed * dt * friction_multiplier
accel_speed = min(accel_speed, add)
```

Only the add-speed calculation observes the 30-unit cap; the acceleration
product uses the original maximum-speed-capped `wish_speed`. Air wish direction
must be normalized and horizontal. No air friction is applied.

## Velocity limits and errors

`clamp_velocity_per_axis` clamps each X/Y/Z component independently to
`[-maximum_velocity, +maximum_velocity]`. Math helpers reject non-finite input,
negative duration/coefficient/speed, non-normalized directions or planes, and
non-finite output. They do not silently normalize invalid external vectors or
convert NaN to zero.

The synthetic `speed` button does not change these equations. Viewer command
movement values determine wish speed, and stock run/walk behavior remains
evidence-pending.
