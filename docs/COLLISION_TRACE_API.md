# Collision trace API

`CollisionWorldQuery` performs deterministic project-owned queries against a
shared immutable `CollisionWorldPackage`. Its compatibility profile is
`project_deterministic_bsp_hull_trace_v1`.

Successful results explicitly carry the evidence profile
`public_bsp_structure_and_independent_fixtures`. Exact stock-engine trace
evidence remains marked `stock_engine_trace_behavior_evidence_pending`.

This API does **not** claim exact stock `PM_PlayerTrace` fractions, stock
`DIST_EPSILON`, stock contents filtering, or stock entity collision behavior.
`stock_engine_trace_behavior_evidence_pending` is deliberately rejected as an
executable profile.

## Query and scratch ownership

A query is constructed with `shared_ptr<const CollisionWorldPackage>`. Every
call also receives a caller-owned `CollisionQueryScratch`. Scratch retains a
bounded frame stack and active-path marks, growing only when first used with a
larger valid package or limit. Reusing one scratch avoids source-sized
allocation on each trace. Concurrent callers use separate scratch objects;
the query and package contain no mutable traversal counters.

The primary operations are:

- `point_contents`: query an exact model/hull terminal;
- `test_position`: apply the explicit contents policy at one point;
- `trace_line`: trace model zero with hull zero;
- `trace_hull`: trace model zero with one exact compiled hull;
- `trace_model_hull`: trace one explicitly selected retained model and hull.

These operations do not compose brush instances or infer class-name solidity.
Rigid transform and multi-object selection belong to the separate explicit
brush/scene layer.

## Point contents

At each plane, the signed distance is

`dot(point, normal) - plane_distance`.

A distance greater than or equal to zero selects the front child; a negative
distance selects the back child. Exact plane ties therefore select the front
child. Hull zero traverses nodes to leaves. Hulls one through three traverse
clipnodes to typed terminals. Traversal is iterative, finite-checked,
cycle-checked, and step-limited.

## Iterative trace

Trace work items retain a typed tree reference, a global segment interval, and
the boundary through which that interval was entered. Internal plane distance,
fraction, interpolation, and comparison calculations use `double`.

For a same-side interval, only that child is visited and the interval is
preserved. For a straddling interval, the crossing is computed as

`d0 / (d0 - d1)`

and mapped into the work item's global interval with checked fused
interpolation. The far child is pushed before the near child, so the LIFO stack
always visits terminal intervals in segment order. Enter/leave frames maintain
active-path marks: a back edge is a cycle, while a completed shared DAG subtree
may be visited again.

The source plane is used for a front-to-back impact. Normal and distance are
both inverted for a back-to-front impact. The published unit normal must face
against the requested motion. The source plane index and orientation are
retained in `CollisionPlaneHit`.

## Tolerance and progress

The default project-owned tolerance profile is:

- plane distance epsilon: `1e-6` source units;
- fraction epsilon: `1e-12`;
- minimum progress fraction: `1e-12`.

Raw distance signs always choose same-side children, so tolerance cannot move
a segment wholly inside solid into the front child. For a genuine crossing,
one endpoint inside the plane epsilon is snapped to the plane only when the
other endpoint lies outside that band. If both crossing endpoints are inside
the band, their raw ratio is retained instead of creating a zero denominator.
Fractions are clamped only when they lie within the fraction epsilon of a valid
bound.
An interior split smaller than the minimum progress threshold fails with
`insufficient_fraction_progress`; it does not guess a child. Exact endpoint
crossings are valid and remain bounded by graph acyclicity, stack limits, split
limits, and traversal-step limits.

These values define project behavior and must not be described as the stock
engine epsilon profile.

## Result semantics

`fraction` is finite and within `[0, 1]`. A no-hit result has fraction `1`, the
requested end position, and no plane or hit. A hit end position is recomputed
from the original segment as `start + fraction * (end - start)`; it is not
accumulated through recursive float interpolation.

An entry reached exactly at the requested endpoint uses that reserved no-hit
form. Its exact blocking `end_contents` is still returned, but a fraction-one
plane or hit is never published.

`start_solid` means the requested start point is blocking under the selected
policy. `all_solid` means the start is blocking and traversal found no
nonblocking interval over the requested segment. The cases are distinct:

| Segment state | Fraction | End | Plane |
| --- | ---: | --- | --- |
| starts free, never enters solid | 1 | requested end | absent |
| starts free, enters solid | earliest entry | interpolated impact | present |
| starts solid, exits and never re-enters | 1 | requested end | absent |
| starts solid, exits and re-enters | earliest re-entry | interpolated impact | present |
| remains solid for the whole interval | 0 | requested start | absent |

An all-solid result has no plane because there is no valid entry boundary. A
zero-length trace is a stationary test: free produces fraction `1`; blocking
produces `start_solid=true`, `all_solid=true`, and fraction `0` without division.

`in_open` and `in_liquid` record whether any traversed terminal interval is
open or liquid. This is an explicit project semantic. The public Valve
`pmtrace_t` comment describes its flags in terms of the endpoint, so these
metadata flags must not be presented as exact stock behavior. Liquid metadata
does not implement swimming.

## Failure behavior

Invalid input, unsupported profiles, malformed package records, cycles,
non-finite arithmetic, stack exhaustion, step exhaustion, split exhaustion,
and scratch-memory exhaustion return typed `CollisionQueryError` values.
Exceeding the configured scratch byte budget reports
`scratch_limit_exceeded`; an allocation failure inside that valid budget
reports `unable_to_prepare_scratch`.
Scratch is reset before return, the package remains immutable, and no partial
`CollisionTraceResult` is published.
