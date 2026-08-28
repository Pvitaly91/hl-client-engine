# GoldSrc BSP collision source

M4.6.3.1 extends the canonical GoldSrc BSP v30 parse with an owning,
renderer-neutral `GoldSrcBspCollisionSource`. The parser decodes the BSP once.
The world geometry, spatial source, brush geometry, entity bytes, and collision
source are all derived from that same validated parser state. Collision code
does not receive the raw BSP and does not implement a second wire parser.

## Ownership boundary

`GoldSrcBspParsedDocument::collision_source` owns copies of the canonical
normalized planes, hull-0 nodes and leaves, clipnodes, and every model record's
collision metadata. It also retains the BSP content fingerprint, source BSP
version, compatibility/evidence profiles, and aggregate validation statistics.
The source owns no native path, raw BSP byte span, renderer object, OpenGL
handle, entity association, or network state.

Every retained record carries its exact source ordinal. Models additionally
retain source origin and bounds, the visible-leaf count, render-face range, and
four typed hull descriptions. Collision planes reuse the already decoded
finite unit normal, finite distance, and source type metadata; they are not
decoded or normalized again.

## Wire and child semantics

A BSP v30 clipnode is exactly eight little-endian bytes:

| Field | Encoding | Meaning |
| --- | --- | --- |
| `planenum` | signed 32-bit | canonical plane index |
| `children[0]` | signed 16-bit | front child |
| `children[1]` | signed 16-bit | back child |

For a clipnode child, a non-negative value is a clipnode index and a negative
value is the exact terminal contents code. Supported contents are the signed
source values `-15` through `-1`. Values outside that profile, invalid positive
indices, invalid plane indices, truncated records, and non-multiple lump sizes
are rejected. Conversion is performed through a widened signed value, so code
does not rely on negating `INT16_MIN`.

Hull 0 uses a different domain. A non-negative node child is a node index. A
negative child identifies a leaf by `leaf_index = -1 - child`; the conversion
and resulting index are checked. The leaf's exact contents value is the
terminal. Typed node, leaf, clipnode, and contents references keep hull-0 and
clipnode indices from being used interchangeably.

Model `headnode[0]` must be a valid node root. Model `headnode[1]` through
`headnode[3]` are clipnode-domain roots. Those three roots may instead be a
supported direct terminal contents value in `-15..-1`; direct terminals remain
typed contents and are counted separately. The parser performs no brush-model
entity or solidity association.

## Graph validation

After all records and references are decoded, the parser validates both graph
domains with bounded iterative tri-color traversal. It visits every model/hull
root first to form the union of reachable nodes or clipnodes, then checks every
unvisited global record. This second pass permits structurally valid unreachable
records while still rejecting a cycle hidden outside the model-root union.

The validator rejects self-cycles, two-node cycles, longer cycles, and work
beyond `maximum_collision_validation_steps`. A previously completed subtree
may be shared by models or hulls and is not a cycle. Traversal uses an explicit
stack; BSP-controlled recursion is not used. Publication is transactional:
reference, cycle, limit, or allocation failure returns an error and no partial
`GoldSrcBspParsedDocument`.

The default validation budget is 262,144 steps and the supported hard ceiling
is 1,048,576. A decoded node or clipnode consumes bounded child-processing and
completion work; duplicate roots and completed shared subtrees do not trigger
recursive re-expansion.

Statistics report source record counts, root-union reachable and unreachable
hull-0 nodes and clipnodes, terminal-reference categories, model hull-root and
direct-terminal-root counts, maximum rooted tree depth, and validation work.
They contain no raw arrays or source strings.

## Compatibility statement

The source profile is
`GoldSrcBspCollisionCompatibilityProfile::valve_bsp_v30_clip_hulls_v1`.
Its evidence profile is
`public_valve_bsp_compiler_and_original_map_validation`, based on the pinned
public Valve BSP/compiler declarations and read-only validation of the required
original maps.

This boundary establishes BSP structure and compiler-hull compatibility. It
does not claim exact stock `PM_PlayerTrace` fractions, engine epsilon behavior,
player hull-selection rules, entity solidity classification, movement, or
prediction compatibility.
