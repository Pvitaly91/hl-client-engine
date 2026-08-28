# GoldSrc movement sliding and steps

Sliding and step selection are bounded collision-query algorithms. They use
the active player hull and an `ILocalMovementCollision` adapter; no renderer,
entity callback or dynamic transform participates.

## Velocity clipping

For normalized collision plane `n` and input velocity `v`:

```text
backoff = dot(v, n) * overbounce
clipped = v - n * backoff
```

The dry-walk kernel uses overbounce `1`. Each component strictly inside
`(-0.1, +0.1)` is then set to zero. Non-normalized/non-finite planes fail.
The public `clip_velocity_against_plane` result also carries the pinned Valve
blocked-axis flags: positive-Z planes report `floor`, exactly vertical planes
report `wall_or_step`, and ceilings report neither. The legacy vector-only
helper is a thin projection of this validated result.

## Bounded slide move

The default loop performs at most four bumps and stores at most five clip
planes. Each bump traces from the current origin to
`origin + velocity * remaining_time`, moves to the trace endpoint, retains an
inert touch for a blocking hit, reduces remaining time by the covered fraction,
and solves velocity against the ordered plane list.

The solver first tries clipping against each plane in encounter order and
accepts the first result that does not enter another plane beyond the stop
epsilon. With exactly two incompatible planes it projects velocity onto their
normalized cross-product crease. Degenerate creases, three-or-more-plane traps
and exhausted remaining movement stop safely at zero velocity. Exceeding a
configured clip-plane bound fails transactionally. Start-solid, all-solid,
missing plane/hit metadata, unsupported liquid and query errors never publish a
partial move.

Every collision-provider result is revalidated inside the pure kernel before
use: enum domains, contents/source-code pairs, hit identity/profile coherence,
closed-unit fractions, finite linearly consistent endpoints, unit planes and
configured stack bounds are fail-closed. This applies to custom implementations
of `ILocalMovementCollision`, not only the built-in adapters.

## Step candidate

For every grounded substep the kernel evaluates two candidates from the same
origin and accelerated velocity:

1. direct bounded slide;
2. trace upward by `environment.step_size`, slide horizontally for the same
   duration, then trace down by `step_size + ground_probe_distance`.

The step candidate is valid only if the upward trace is completely clear, the
down trace lands on a hit with `normal.z >= 0.7`, and a stationary destination
hull test is free. A blocked rise, too-high obstacle, missing landing, steep
landing or blocked clearance simply makes that candidate unavailable.
An actual `startsolid`/`allsolid` returned by the horizontal step slide fails
the whole command transactionally, just like the direct slide. Any step-up or
step-down trace that reports crossing liquid also fails with the typed
unsupported-liquid result; a dry endpoint cannot hide a vertical liquid cross.

Candidate selection compares squared horizontal progress. The step wins only
when it is valid and strictly farther than the direct path. Equal progress
keeps the direct path, making ties deterministic. A selected step retains the
horizontal slide velocity and increments `step_success_count`; ground
categorization then performs the ordinary two-unit snap.

The step height comes directly from validated MoveVars. The offline fixture
uses 18 units. No stair animation, moving platform, base velocity, conveyor,
dynamic brush transform or automatic unsticking is implemented.

## Collision roles

`WorldOnlyMovementCollision` is the production/default path.
`SyntheticBrushMovementCollision` may include only brush instances already
marked with the explicit synthetic static-solid role. The movement layer does
not infer solidity from `func_door`, `func_wall`, classname or render presence.
