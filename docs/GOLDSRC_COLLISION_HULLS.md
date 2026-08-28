# GoldSrc collision hulls

The canonical BSP collision source publishes four named compiler hulls for
every BSP model. The ordinals, tree domains, and extents are explicit data; an
arbitrary bounding box is not converted into a guessed hull.

## Compiler profile

| Hull | Neutral name | Tree domain | Minimum | Maximum |
| ---: | --- | --- | --- | --- |
| 0 | `point` | node/leaf | `(0, 0, 0)` | `(0, 0, 0)` |
| 1 | `standing_32x32x72` | clipnode/contents | `(-16, -16, -36)` | `(16, 16, 36)` |
| 2 | `large_64_cube` | clipnode/contents | `(-32, -32, -32)` | `(32, 32, 32)` |
| 3 | `duck_32x32x36` | clipnode/contents | `(-16, -16, -18)` | `(16, 16, 18)` |

These values are the public Valve compiler profile represented by
`valve_bsp_v30_clip_hulls_v1`. They describe the four hulls compiled into BSP
model records. They do not define runtime player hull selection, duck-state
transitions, mod-specific hulls, or arbitrary AABB expansion.

## Hull 0: node and leaf domain

Hull 0 begins at `model.headnode[0]`, which is validated as a non-negative node
index. Nodes reference the canonical shared plane list. Child zero is the front
half-space and child one is the back half-space. A non-negative child continues
to another node; a negative child converts to a leaf with `-1 - child`. The
leaf retains the exact signed BSP contents terminal.

Hull 0 never indexes the clipnode array. Its root is a typed node reference and
its children are typed node-or-leaf variants.

## Hulls 1–3: clipnode and contents domain

`model.headnode[1]`, `[2]`, and `[3]` use the clipnode domain. A non-negative
root or child is a clipnode index. A negative value is a direct, exact contents
terminal, limited to the supported BSP profile `-15..-1`. These hulls never
look up a render node or leaf.

Each published hull carries its ordinal, `clipnode_contents` domain, root,
extents, compatibility profile, and evidence profile. A negative model root is
retained as a typed `GoldSrcContentsCode`, not converted to an index or coerced
to solid.

## Reachability and sharing

Reachability is the union from every model root of the matching domain. Models
and hulls may share completed acyclic subtrees. The parser counts records that
are outside the root union, but still validates every global node and clipnode
for valid planes, valid children, supported terminals, and cycles. Iterative
tri-color traversal rejects back-edges while accepting visits to completed
subtrees.

Traversal is bounded by the parser's collision-validation step limit. A limit
failure or malformed graph prevents the entire parsed document from being
published.

## Scope boundary

The four hulls are immutable collision-source metadata. The BSP parser does
not decide which hull a future movement state should use, does not infer brush
solidity from class names, and does not implement movement, prediction, or
network behavior. Exact stock runtime trace behavior remains evidence-pending.
