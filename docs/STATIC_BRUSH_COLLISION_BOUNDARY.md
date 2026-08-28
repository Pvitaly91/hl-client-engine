# Static brush collision boundary

M4.6.3.1 exposes BSP collision models 1..N for explicit, renderer-neutral
queries. This is a static composition boundary, not GoldSrc entity gameplay
simulation. The collision package shares the canonical BSP planes and hull
trees and does not parse the BSP source again.

## Explicit rigid trace

`trace_explicit_brush_model` accepts one retained `BrushCollisionModel`, a
validated `BrushRigidTransform`, a world-space segment, and one of the four
exact compiler hull ordinals. The supported transform is translation plus the
Valve entity pitch/yaw/roll rotation profile; scale and nonzero source-model
origins are rejected.

For an indexed node/clipnode root, the query performs these operations:

1. During immutable library construction, traverse the selected hull tree and
   derive a conservative blocking-space AABB only from exact axial path
   constraints. Non-axial constraints are ignored, which can enlarge the
   proof but cannot shrink it.
2. Transform the world start and end into model-local space using the same
   operation consumed by the exact trace.
3. Use segment-versus-proven-AABB only to reject a definite miss.
4. Run `CollisionWorldQuery::trace_model_hull` against that model's compiled
   hull tree.
5. Preserve the local trace fraction and recompute the world end position from
   the original world segment.
6. Rotate the hit normal and translate its plane distance back to world space.

The broad phase is not collision geometry. A rotated brush is always resolved
by its BSP hull; it is never approximated or replaced by a world AABB. Source
`dmodel` bounds are retained and transform-validated but never authorize a
rejection because structural BSP validity alone does not prove that a hull is
enclosed by them. Every derived plane bound and published transformed bound is
rounded outward. If any reachable blocking cell is unbounded, malformed,
cyclic, beyond the proof budget, or not finitely bounded by exact axial
constraints, proof is absent and the broad phase must pass the candidate to
the exact trace. Segment/slab fractions use outward intervals for both
subtraction and division; an interval that cannot exclude zero or otherwise
cannot remain finite also passes the candidate to the exact trace. False
positives are permitted; false negatives are not.

A directly typed terminal hull root describes the whole queried model-local
space rather than a bounded subtree. It therefore has no finite proof and
always reaches the exact terminal query, preserving empty, liquid, solid,
`start_solid`, and `all_solid` semantics.

## Explicit role evidence

`IBrushCollisionRoleProvider` produces one typed role for an exact stable
instance identity: `solid`, `non_solid`, `unsupported`, or
`evidence_pending`. Identity contains the stable instance ordinal, source model
index, and optional source entity index.

Two evidence profiles are supported:

- `explicit_synthetic_brush_solidity_v1` applies only exact caller-provided
  synthetic bindings. An absent binding remains `evidence_pending`; duplicate
  bindings fail closed as `unsupported`.
- `stock_brush_solidity_evidence_pending` always returns
  `evidence_pending`. It never derives solidity from classname, model name,
  render mode, or the existence of a BSP submodel.

Consequently the production stock scene contains world collision only by
default. Explicit brush collision remains available to tests, tools, and a
future runtime provider with sufficient state evidence.

## Multi-object ordering

Scene queries include world model 0 by default and trace only explicitly
`solid` brush instances. A request can exclude world collision, provide an
exact brush-instance identity allowlist, and ignore one exact instance; the
ignore filter wins over an allowlist match. An absent allowlist considers all
retained instances, while a present empty allowlist considers none.

Among the selected objects, the smallest fraction wins. Fractions within the
declared project tolerance use this deterministic order:

1. world;
2. lowest stable instance ordinal;
3. lowest source model index.

Instances are retained in the same stable ordering, so broad-phase and bounded
candidate-limit failures are reproducible. Non-solid, unsupported, and
evidence-pending instances remain visible as typed scene metadata but are not
traced as blockers.

## Deferred evidence

This milestone does not infer stock brush solidity, execute spawn logic, or
interpret `solid`, `movetype`, use/touch/think functions, door/platform state,
or mod/game-DLL behavior. Moving and rotating runtime brushes, dynamic
transforms, and entity-state synchronization remain deferred.

Scene-wide `start_solid`, open, and liquid evidence can be combined across
queried objects. Scene-wide `all_solid` cannot be proven false merely by
selecting one object's earliest trace: overlapping solid intervals from
multiple objects could cover the complete segment. The API therefore reports
one of four explicit classifications:

- `exact_without_model_trace` when world is excluded and no eligible solid
  brush reaches an exact BSP trace, including exact-safe broad-phase misses;
- `exact_from_world_only` when no brush model was traced;
- `proven_true_by_single_object` when one object alone is all-solid;
- `multi_object_interval_union_evidence_pending` otherwise.

Exact bounded union-of-blocking-interval semantics are intentionally deferred;
the selected object's `CollisionTraceResult::all_solid == false` is not a
scene-wide claim in the evidence-pending state.
