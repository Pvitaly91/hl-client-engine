# Collision world package

`hlclient_collision_api` is the renderer-, platform-, filesystem-, and
network-neutral ownership boundary for collision queries. The package contains
only validated numeric records and source metadata. It never retains BSP
bytes, an `AssetSource`, a native path, entity snapshots, renderer objects, or
network state.

## Ownership and identity

`CollisionWorldPackage` owns one plane array, the hull-0 node/leaf arrays, the
clipnode array, and an ordered model array. Models reference those common
arrays by checked indices. Model zero is the world collision model; later
models are addressable collision models but are not automatically classified
as solid scene objects.

The public constructor is the handoff used by format-specific transactional
builders. After construction, arrays are exposed only as const spans. A source
fingerprint and revision may associate the package with the canonical asset
parse without retaining source bytes or a path. Queries defensively validate
every traversed record even though a production builder is expected to publish
only a complete valid package.

The supported structure profile is
`valve_bsp_v30_clip_hulls_v1`, backed by
`public_valve_bsp_compiler_and_original_map_validation`. This describes BSP
structure compatibility. It is not a claim that stock engine movement traces
have been reproduced.

## Typed tree domains

Hull zero uses only the node/leaf domain. A `CollisionNodeChild` is explicitly
either a node index or a leaf index. It is never converted through clipnode
indexing.

Hulls one through three use only the clipnode domain. A
`CollisionClipnodeChild` is explicitly either another clipnode index or a
typed terminal. These hulls may also have a directly typed terminal root.
Signed BSP encodings do not survive as ambiguous package indices.

Each model exposes exactly four `CollisionHull` records:

| Ordinal | Neutral name | Clip minimum | Clip maximum |
| --- | --- | --- | --- |
| 0 | `point` | `(0, 0, 0)` | `(0, 0, 0)` |
| 1 | `standing_32x32x72` | `(-16, -16, -36)` | `(16, 16, 36)` |
| 2 | `large_64_cube` | `(-32, -32, -32)` | `(32, 32, 32)` |
| 3 | `duck_32x32x36` | `(-16, -16, -18)` | `(16, 16, 18)` |

These are BSP compiler hull ordinals. They must not be confused with the
runtime `playermove_t::usehull` numbering shown in the public SDK. The package
does not implement arbitrary AABB expansion, runtime hull selection, or duck
transition rules.

## Contents

`GoldSrcContentsCode` preserves the exact signed BSP terminal in the supported
range `-15` through `-1`. `CollisionContentsCategory` maps each value
individually to empty, solid, water, slime, lava, sky, origin, clip, one of the
six currents, or translucent. Unknown values are rejected; numeric range
shortcuts are not used to infer behavior.

The only executable blocking policy in this milestone is
`project_solid_only_v1`. It blocks exactly the `solid` category. Water, slime,
lava, currents, sky, origin, clip, and translucent are not silently coerced to
solid. `stock_player_trace_contents_policy_pending` is metadata only and is
rejected by queries.

`is_open_space`, `is_liquid`, `is_current`, `is_solid_geometry`, and
`is_special` are pure category helpers. Currents are reported as liquid
metadata as well as current metadata; this does not add swimming or current
movement.

## Security boundary

Queries reject non-finite planes, invalid source types, non-unit normals,
invalid child kinds and indices, mismatched raw/category contents, invalid
model or hull metadata, cycles on the active traversal path, excessive work,
and insufficient scratch capacity. Shared acyclic subtrees are legal.

All count, stack, and scratch-memory limits have hard caps. Failure returns a
typed error and does not publish a partial query result or mutate the package.
Separate caller-owned scratch objects make queries reentrant. No package or
query operation performs filesystem, network, renderer, SDL, or OpenGL work.

## Deliberate stop point

The package supplies point contents, stationary tests, and single-model hull
traces. It does not provide movement, friction, gravity, acceleration,
jumping, steps, stuck recovery, prediction, replay, reconciliation, dynamic
brush composition, entity solidity inference, or Studio hitbox collision.
